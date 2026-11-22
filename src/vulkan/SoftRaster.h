// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A CPU renderer that runs a DrawList through exactly the arithmetic of shaders/ui.frag. The Vulkan backend cannot run on a
// build server without a GPU; this one can, so the whole user interface is tested and screenshotted everywhere.
#include <cstdint>
#include <vector>
#include "Draw.h"
#include "Text.h"

namespace vkui {

struct SoftImage {
    int w = 0, h = 0;
    std::vector<std::uint32_t> px;   // 0xAARRGGBB
    std::uint32_t at(int x, int y) const { return px[size_t(y) * size_t(w) + size_t(x)]; }
    bool savePpm(const std::string& path) const;
};

SoftImage softRender(const DrawList& list, const Fonts& fonts, Color clear);

}  // namespace vkui
