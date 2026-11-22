// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExportFormats.h"

#include <algorithm>

#include "FileTypes.h"

namespace core {

const std::vector<std::string>& exportFormatNames() {
    static const std::vector<std::string> v = {"WAV 16-bit", "WAV 24-bit", "WAV 32-bit float", "AIFF 16-bit", "AIFF 24-bit", "MP3"};
    return v;
}
const std::vector<std::string>& exportRateNames() {
    static const std::vector<std::string> v = {"44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"};
    return v;
}
const std::vector<std::string>& exportMp3RateNames() {
    static const std::vector<std::string> v = {"128 kbps", "160 kbps", "192 kbps", "224 kbps", "256 kbps", "320 kbps"};
    return v;
}

void applyExportChoices(ExportOptions& opt, int formatIndex, int rateIndex, int mp3Index) {
    static const int kRates[] = {44100, 48000, 88200, 96000};
    static const int kKbps[] = {128, 160, 192, 224, 256, 320};
    formatIndex = std::clamp(formatIndex, 0, 5);
    opt.container = formatIndex <= 2 ? ExportContainer::Wav : formatIndex <= 4 ? ExportContainer::Aiff : ExportContainer::Mp3;
    switch (formatIndex) {
    case 0: case 3: opt.format = dsp::WavFormat::Pcm16; break;
    case 1: case 4: opt.format = dsp::WavFormat::Pcm24; break;
    case 2: opt.format = dsp::WavFormat::Float32; break;
    default: opt.format = dsp::WavFormat::Pcm16; break;   // MP3: the encoder takes 16-bit
    }
    opt.sampleRate = kRates[std::clamp(rateIndex, 0, 3)];
    opt.mp3Kbps = kKbps[std::clamp(mp3Index, 0, 5)];
}

std::string exportExtension(ExportContainer c) { return c == ExportContainer::Mp3 ? ".mp3" : c == ExportContainer::Aiff ? ".aiff" : ".wav"; }

std::string exportFilter(ExportContainer c) {
    return c == ExportContainer::Mp3 ? "MP3 audio|*.mp3" : c == ExportContainer::Aiff ? "AIFF audio|*.aiff;*.aif" : "WAV audio|*.wav";
}

std::string withExportExtension(const std::string& path, ExportContainer c) {
    const std::string e = fileExtension(path);
    if (c == ExportContainer::Aiff && (e == ".aif" || e == ".aiff")) return path;
    if (e == exportExtension(c)) return path;
    // a known audio extension of another type is replaced, anything else (a dot inside a name) is kept and the extension appended
    std::string base = path;
    if (!e.empty() && classifyFile(path) == FileKind::Audio) base = path.substr(0, path.size() - e.size());
    return base + exportExtension(c);
}

}  // namespace core
