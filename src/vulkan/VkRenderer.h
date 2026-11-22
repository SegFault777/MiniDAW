// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI, layer 0: the GPU backend. It takes a DrawList and the glyph atlas and puts them on a window: one pipeline, one
// descriptor set, dynamic scissor per batch. vulkan-1.dll is loaded at run time, so the program starts (and says what is
// missing) on a computer without a Vulkan driver - only the Vulkan headers are needed to build.
#include <memory>
#include <string>
#include "Draw.h"
#include "Text.h"

namespace vkui {

class VkRenderer {
public:
    VkRenderer();
    ~VkRenderer();
    VkRenderer(const VkRenderer&) = delete;
    VkRenderer& operator=(const VkRenderer&) = delete;

    /// hinstance / hwnd: the Win32 window. shaderDir holds ui.vert.spv and ui.frag.spv. On failure 'error' says why.
    bool init(void* hinstance, void* hwnd, const std::string& shaderDir, std::string& error);
    void shutdown();
    /// The window size changed (device pixels).
    void resize(int width, int height);
    /// Draws one frame. Returns false when nothing was presented (minimised window, swap chain being rebuilt): try again.
    bool render(const DrawList& list, const Fonts& fonts, Color clear);
    std::string gpuName() const;
    int width() const;
    int height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

}  // namespace vkui
