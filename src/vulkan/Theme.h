// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// <windows.h> (rpcndr.h) defines "small" as a macro for char, and this header has a text style called small.
#ifdef small
#undef small
#endif
// Vulkan UI: the "Aurora" look - a calm blue-grey dark theme with one accent, and a colour per track.
#include "Draw.h"

namespace vkui {
namespace theme {

constexpr Color bg0 = rgb(0x12, 0x14, 0x19);       // window, behind everything
constexpr Color bg1 = rgb(0x1a, 0x1d, 0x24);       // panels
constexpr Color bg2 = rgb(0x23, 0x27, 0x30);       // raised controls, lanes
constexpr Color bg3 = rgb(0x2d, 0x32, 0x3d);       // hover
constexpr Color bg4 = rgb(0x3a, 0x40, 0x4e);       // pressed / strong hover
constexpr Color line = rgb(0x0b, 0x0d, 0x10);      // dark separators
constexpr Color border = rgb(0x34, 0x3a, 0x47);    // light separators, outlines
constexpr Color text = rgb(0xe8, 0xeb, 0xf1);
constexpr Color textDim = rgb(0xa2, 0xab, 0xbb);
constexpr Color textMute = rgb(0x6c, 0x76, 0x88);
constexpr Color accent = rgb(0x5b, 0x8c, 0xff);
constexpr Color accentDim = rgb(0x2b, 0x44, 0x80);
constexpr Color play = rgb(0x3d, 0xdc, 0x84);
constexpr Color record = rgb(0xff, 0x4d, 0x5a);
constexpr Color solo = rgb(0xff, 0xc5, 0x3d);
constexpr Color mute = rgb(0xff, 0x8a, 0x3d);
constexpr Color loop = rgb(0xf5, 0xb9, 0x42);
constexpr Color warn = rgb(0xff, 0xb0, 0x20);
constexpr Color meterLow = rgb(0x35, 0xd0, 0x7a);
constexpr Color meterMid = rgb(0xe6, 0xd2, 0x3c);
constexpr Color meterHigh = rgb(0xff, 0x4d, 0x4d);
constexpr Color lcdBg = rgb(0x0c, 0x10, 0x16);
constexpr Color lcdText = rgb(0x9f, 0xe3, 0xff);
constexpr Color playhead = rgb(0xff, 0xff, 0xff);

constexpr float radius = 5.f;
constexpr float rowH = 24.f;

constexpr TextStyle small{11, false};
constexpr TextStyle normal{12, false};
constexpr TextStyle bold{12, true};
constexpr TextStyle title{13, true};
constexpr TextStyle big{20, true};

/// Twelve track colours (Logic's palette, a little softer).
inline Color trackColor(int i) {
    static const Color c[12] = {rgb(0x5b, 0x8c, 0xff), rgb(0x3d, 0xc7, 0x9a), rgb(0xf2, 0xa3, 0x3a), rgb(0xe8, 0x62, 0x8a), rgb(0x9b, 0x6b, 0xf0), rgb(0x4f, 0xc3, 0xe8),
                                rgb(0xb9, 0xd9, 0x4a), rgb(0xf0, 0x6d, 0x4f), rgb(0x6f, 0xa6, 0x6a), rgb(0xd0, 0x7a, 0xd8), rgb(0x8e, 0x9a, 0xb4), rgb(0xe3, 0xc3, 0x6b)};
    return c[((i % 12) + 12) % 12];
}

}  // namespace theme
}  // namespace vkui
