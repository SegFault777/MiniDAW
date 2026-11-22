// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LogicProjectData.h"

#include <algorithm>
#include <map>
#include <system_error>

#include "Files.h"
#include "LogicArrangement.h"

namespace core {

namespace {
std::vector<ProjectDataDecoder>& decoders() { static std::vector<ProjectDataDecoder> d; return d; }
bool printable(std::uint8_t c) { return c >= 0x20 && c < 0x7F; }
bool tagChar(std::uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
}  // namespace

bool registerProjectDataDecoder(ProjectDataDecoder d) {
    if (!d) return false;
    decoders().push_back(std::move(d));
    return true;
}
void clearProjectDataDecoders() { decoders().clear(); }

ProjectDataSurvey surveyProjectData(const std::uint8_t* data, std::size_t size) {
    ProjectDataSurvey sv;
    sv.size = size;
    if (size >= 4) sv.magic = std::uint32_t(data[0]) << 24 | std::uint32_t(data[1]) << 16 | std::uint32_t(data[2]) << 8 | data[3];
    std::map<std::string, int> tags;
    for (std::size_t i = 0; i + 4 <= size; ++i)
        if (tagChar(data[i]) && tagChar(data[i + 1]) && tagChar(data[i + 2]) && tagChar(data[i + 3])) {
            ++tags[std::string(reinterpret_cast<const char*>(data + i), 4)];
            i += 3;
        }
    for (const auto& [t, n] : tags) if (n >= 4) sv.fourCCs.push_back({t, n});
    std::sort(sv.fourCCs.begin(), sv.fourCCs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (sv.fourCCs.size() > 40) sv.fourCCs.resize(40);
    // ASCII strings
    for (std::size_t i = 0; i < size && sv.strings.size() < 200;) {
        std::size_t j = i;
        while (j < size && printable(data[j])) ++j;
        if (j - i >= 4) sv.strings.emplace_back(reinterpret_cast<const char*>(data + i), j - i);
        i = j + 1;
    }
    return sv;
}

// ---- the built-in decoder: the chunk structure of ProjectData as written by Logic Pro X 10.x (found with a real 10.5 project)
//   file    = 24-byte header (magic 23 47 C0 AB, ..., u32 LE total length at +16) + chunks
//   chunk   = 36-byte header + payload;  header: 4-letter type written backwards ("gnoS" = "Song"), 28 bytes of ids / flags,
//             u32 LE payload length at +28 (+32: 4 zero bytes); the next chunk follows the payload directly
//   the chain of a real file ends exactly at the end of the file (3027 chunks in the sample).
// Decoded so far: the chunk inventory and the media pool (AuFl chunks: u16 name length at +8, then UTF-16LE name).
// Not decoded: where regions sit on the timeline, MIDI notes, track names (see docs/dev/logicx.md for what is known).
namespace {
bool decodeLogic10(const std::uint8_t* d, std::size_t n, LogicProject& out, std::string& name) {
    if (n < 24 + 36 || d[0] != 0x23 || d[1] != 0x47 || d[2] != 0xC0 || d[3] != 0xAB) return false;
    auto u32 = [&](std::size_t o) { return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 | std::uint32_t(d[o + 2]) << 16 | std::uint32_t(d[o + 3]) << 24; };
    std::map<std::string, int> counts;
    std::size_t o = 24;
    int chunks = 0;
    while (o + 36 <= n) {
        std::string tag(reinterpret_cast<const char*>(d + o), 4);
        for (char c : tag) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) { tag.clear(); break; }
        if (tag.empty()) break;
        const std::uint32_t size = u32(o + 28);
        if (std::uint64_t(o) + 36 + size > n) break;
        std::reverse(tag.begin(), tag.end());
        ++counts[tag];
        if (chunks == 0 && tag != "Song") return false;
        if (tag == "AuFl" && size >= 10) {
            const std::size_t p = o + 36;
            const std::size_t len = std::size_t(d[p + 8]) | std::size_t(d[p + 9]) << 8;
            if (10 + 2 * len <= size) {
                std::string s;
                for (std::size_t i = 0; i < len; ++i) {   // UTF-16LE -> UTF-8 (BMP only is enough for file names; others become '?')
                    const std::uint32_t cp = std::uint32_t(d[p + 10 + 2 * i]) | std::uint32_t(d[p + 11 + 2 * i]) << 8;
                    if (cp < 0x80) s += char(cp);
                    else if (cp < 0x800) { s += char(0xC0 | cp >> 6); s += char(0x80 | (cp & 0x3F)); }
                    else if (cp >= 0xD800 && cp < 0xE000) s += '?';
                    else { s += char(0xE0 | cp >> 12); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
                }
                out.mediaPool.push_back(s);
            }
        }
        ++chunks;
        o += 36 + size;
    }
    if (chunks == 0) return false;
    const LogicArrangementStats as = decodeLogicArrangement(d, n, out);
    for (const auto& [t, c] : counts) out.chunkCounts.push_back({t, c});
    std::sort(out.chunkCounts.begin(), out.chunkCounts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    name = std::string("Logic Pro X reader (") + std::to_string(chunks) + " chunks" + (o == n ? ", chain ends at the end of the file" : ", chain stops early") +
           "; " + std::to_string(as.audioRegions) + " audio and " + std::to_string(as.midiRegions) + " MIDI regions, " + std::to_string(as.notes) + " notes, " +
           std::to_string(as.tempoPoints) + " tempo point(s), " + std::to_string(as.markers) + " marker(s))";
    return true;
}
}  // namespace

ProjectDataResult parseProjectData(const std::uint8_t* data, std::size_t size) {
    ProjectDataResult r;
    r.survey = surveyProjectData(data, size);
    if (!data || size < 16) { r.note = "ProjectData is empty or too short."; return r; }
    for (const auto& d : decoders()) {
        LogicProject p;
        std::string name;
        if (d(data, size, p, name)) {
            r.decoded = !p.empty();
            r.project = std::move(p);
            r.note = r.decoded ? "Decoded by " + name + "." : name + " recognised the file but found no tracks or tempo.";
            return r;
        }
    }
    {
        LogicProject p; std::string name;
        if (decodeLogic10(data, size, p, name)) {
            r.decoded = !p.empty();
            r.project = std::move(p);
            r.note = "Decoded by " + name + ".";
            return r;
        }
    }
    r.note = "No decoder understands this ProjectData layout (Logic's format is proprietary and not documented).";
    return r;
}

std::string findProjectDataFile(const std::string& packageFolder) {
    std::error_code ec;
    const std::string alt = packageFolder + "/Alternatives";
    if (!fs::is_directory(toPath(alt), ec)) return {};
    std::vector<std::string> found;
    for (fs::directory_iterator it(toPath(alt), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string f = toUtf8(it->path()) + "/ProjectData";
        std::error_code e2;
        if (fs::is_regular_file(toPath(f), e2)) found.push_back(f);
    }
    if (found.empty()) return {};
    std::sort(found.begin(), found.end());
    return found.back();
}

}  // namespace core
